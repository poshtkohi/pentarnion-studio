#pragma once
#include "research/script_builder.hpp"
#include <QWidget>
#include <array>
class QStackedWidget;class QComboBox;class QDoubleSpinBox;class QSpinBox;class QLineEdit;class QLabel;
namespace penta {
class ResearchSpatialControls final:public QWidget {
    Q_OBJECT
public:
    explicit ResearchSpatialControls(QWidget* parent=nullptr);
    research::SpatialConfiguration configuration()const;
    void setConfiguration(const research::SpatialConfiguration&);
    void setMode(research::Mode);
signals:
    void changed();
private:
    QStackedWidget* m_pages=nullptr;bool m_loading=false;
    QComboBox *m_dimension=nullptr,*m_valueType=nullptr,*m_pdeMethod=nullptr,*m_integrator=nullptr,*m_boundary=nullptr,*m_embedding=nullptr,*m_form=nullptr,*m_mechanicsMethod=nullptr,*m_constraintMode=nullptr;
    QSpinBox *m_nx=nullptr,*m_ny=nullptr,*m_component=nullptr,*m_nu=nullptr,*m_nv=nullptr,*m_mechanicsDimensions=nullptr,*m_constraints=nullptr;
    QDoubleSpinBox *m_lengthX=nullptr,*m_lengthY=nullptr,*m_diffusion=nullptr,*m_pdeStep=nullptr,*m_u0=nullptr,*m_u1=nullptr,*m_v0=nullptr,*m_v1=nullptr,*m_mechanicsStep=nullptr,*m_robinAlpha=nullptr;
    QLineEdit *m_initial=nullptr,*m_reaction=nullptr,*m_boundaryValue=nullptr,*m_expression=nullptr,*m_q0=nullptr,*m_second0=nullptr,*m_mass=nullptr,*m_constraintExpression=nullptr;
    std::array<QLineEdit*,5> m_coordinates{};std::array<QSpinBox*,3> m_projection{};
    QLabel* m_secondLabel=nullptr;
    void markChanged();void updatePde();void updateGeometry();void updateMechanics(bool loadTemplate);
};
}
